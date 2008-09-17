/*
 * WHCI UWB Multi-interface Controller enumerator.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This file is released under the GNU GPL v2.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/uwb/whci.h>
#include <linux/uwb/umc.h>

struct whci_card {
	struct pci_dev *pci;
	void __iomem *uwbbase;
	u8 n_caps;
	struct umc_dev *devs[0];
};


/* Fix faulty HW :( */
static
u64 whci_capdata_quirks(struct whci_card *card, u64 capdata)
{
	u64 capdata_orig = capdata;
	struct pci_dev *pci_dev = card->pci;
	if (pci_dev->vendor == PCI_VENDOR_ID_INTEL
	    && (pci_dev->device == 0x0c3b || pci_dev->device == 0004)
	    && pci_dev->class == 0x0d1010) {
		switch (UWBCAPDATA_TO_CAP_ID(capdata)) {
			/* WLP capability has 0x100 bytes of aperture */
		case 0x80:
			capdata |= 0x40 << 8; break;
			/* WUSB capability has 0x80 bytes of aperture
			 * and ID is 1 */
		case 0x02:
			capdata &= ~0xffff;
			capdata |= 0x2001;
			break;
		}
	}
	if (capdata_orig != capdata)
		dev_warn(&pci_dev->dev,
			 "PCI v%04x d%04x c%06x#%02x: "
			 "corrected capdata from %016Lx to %016Lx\n",
			 pci_dev->vendor, pci_dev->device, pci_dev->class,
			 (unsigned)UWBCAPDATA_TO_CAP_ID(capdata),
			 (unsigned long long)capdata_orig,
			 (unsigned long long)capdata);
	return capdata;
}


/**
 * whci_wait_for - wait for a WHCI register to be set
 *
 * Polls (for at most @max_ms ms) until '*@reg & @mask == @result'.
 */
int whci_wait_for(struct device *dev, u32 __iomem *reg, u32 mask, u32 result,
	unsigned long max_ms, const char *tag)
{
	unsigned t = 0;
	u32 val;
	for (;;) {
		val = le_readl(reg);
		if ((val & mask) == result)
			break;
		msleep(10);
		if (t >= max_ms) {
			dev_err(dev, "timed out waiting for %s ", tag);
			return -ETIMEDOUT;
		}
		t += 10;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(whci_wait_for);


/*
 * NOTE: the capinfo and capdata registers are slightly different
 *       (size and cap-id fields). So for cap #0, we need to fill
 *       in. Size comes from the size of the register block
 *       (statically calculated); cap_id comes from nowhere, we use
 *       zero, that is reserved, for the radio controller, because
 *       none was defined at the spec level.
 */
static int whci_add_cap(struct whci_card *card, int n)
{
	struct umc_dev *umc;
	u64 capdata;
	int bar, err;

	umc = umc_device_create(&card->pci->dev, n);
	if (umc == NULL)
		return -ENOMEM;

	capdata = le_readq(card->uwbbase + UWBCAPDATA(n));

	bar = UWBCAPDATA_TO_BAR(capdata) << 1;

	capdata = whci_capdata_quirks(card, capdata);
	/* Capability 0 is the radio controller. It's size is 32
	 * bytes (WHCI0.95[2.3, T2-9]). */
	umc->version         = UWBCAPDATA_TO_VERSION(capdata);
	umc->cap_id          = n == 0 ? 0 : UWBCAPDATA_TO_CAP_ID(capdata);
	umc->bar	     = bar;
	umc->resource.start  = pci_resource_start(card->pci, bar)
		+ UWBCAPDATA_TO_OFFSET(capdata);
	umc->resource.end    = umc->resource.start
		+ (n == 0 ? 0x20 : UWBCAPDATA_TO_SIZE(capdata)) - 1;
	umc->resource.name   = umc->dev.bus_id;
	umc->resource.flags  = card->pci->resource[bar].flags;
	umc->resource.parent = &card->pci->resource[bar];
	umc->irq             = card->pci->irq;

	err = umc_device_register(umc);
	if (err < 0)
		goto error;
	card->devs[n] = umc;
	return 0;

error:
	kfree(umc);
	return err;
}

static void whci_del_cap(struct whci_card *card, int n)
{
	struct umc_dev *umc = card->devs[n];

	if (umc != NULL)
		umc_device_unregister(umc);
}

static int whci_n_caps(struct pci_dev *pci)
{
	void __iomem *uwbbase;
	u64 capinfo;

	uwbbase = pci_iomap(pci, 0, 8);
	if (!uwbbase)
		return -ENOMEM;
	capinfo = le_readq(uwbbase + UWBCAPINFO);
	pci_iounmap(pci, uwbbase);

	return UWBCAPINFO_TO_N_CAPS(capinfo);
}

static int whci_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	struct whci_card *card;
	int err, n_caps, n;

	err = pci_enable_device(pci);
	if (err < 0)
		goto error;
	pci_enable_msi(pci);
	pci_set_master(pci);
	err = -ENXIO;
	if (!pci_set_dma_mask(pci, DMA_64BIT_MASK))
		pci_set_consistent_dma_mask(pci, DMA_64BIT_MASK);
	else if (!pci_set_dma_mask(pci, DMA_32BIT_MASK))
		pci_set_consistent_dma_mask(pci, DMA_32BIT_MASK);
	else
		goto error_dma;

	err = n_caps = whci_n_caps(pci);
	if (n_caps < 0)
		goto error_ncaps;

	err = -ENOMEM;
	card = kzalloc(sizeof(struct whci_card)
		       + sizeof(struct whci_dev *) * (n_caps + 1),
		       GFP_KERNEL);
	if (card == NULL)
		goto error_kzalloc;
	card->pci = pci;
	card->n_caps = n_caps;

	err = -EBUSY;
	if (!request_mem_region(pci_resource_start(pci, 0),
				UWBCAPDATA_SIZE(card->n_caps),
				"whci (capability data)"))
		goto error_request_memregion;
	err = -ENOMEM;
	card->uwbbase = pci_iomap(pci, 0, UWBCAPDATA_SIZE(card->n_caps));
	if (!card->uwbbase)
		goto error_iomap;

	/* Add each capability. */
	for (n = 0; n <= card->n_caps; n++) {
		err = whci_add_cap(card, n);
		if (err < 0 && n == 0) {
			dev_err(&pci->dev, "cannot bind UWB radio controller:"
				" %d\n", err);
			goto error_bind;
		}
		if (err < 0)
			dev_warn(&pci->dev, "warning: cannot bind capability "
				 "#%u: %d\n", n, err);
	}
	pci_set_drvdata(pci, card);
	return 0;

error_bind:
	pci_iounmap(pci, card->uwbbase);
error_iomap:
	release_mem_region(pci_resource_start(pci, 0), UWBCAPDATA_SIZE(card->n_caps));
error_request_memregion:
	kfree(card);
error_kzalloc:
error_ncaps:
error_dma:
	pci_disable_msi(pci);
	pci_disable_device(pci);
error:
	return err;
}

static void whci_remove(struct pci_dev *pci)
{
	struct whci_card *card = pci_get_drvdata(pci);
	int n;

	pci_set_drvdata(pci, NULL);
	/* Unregister each capability in reverse (so the master device
	 * is unregistered last). */
	for (n = card->n_caps; n >= 0 ; n--)
		whci_del_cap(card, n);
	pci_iounmap(pci, card->uwbbase);
	release_mem_region(pci_resource_start(pci, 0), UWBCAPDATA_SIZE(card->n_caps));
	kfree(card);
	pci_disable_msi(pci);
	pci_disable_device(pci);
}

static struct pci_device_id whci_id_table[] = {
	{ PCI_DEVICE_CLASS(PCI_CLASS_WIRELESS_WHCI, ~0) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, whci_id_table);


static struct pci_driver whci_driver = {
	.name     = "whci",
	.id_table = whci_id_table,
	.probe    = whci_probe,
	.remove   = whci_remove,
};

static int __init whci_init(void)
{
	return pci_register_driver(&whci_driver);
}

static void __exit whci_exit(void)
{
	pci_unregister_driver(&whci_driver);
}

module_init(whci_init);
module_exit(whci_exit);

MODULE_DESCRIPTION("WHCI UWB Multi-interface Controller enumerator");
MODULE_AUTHOR("Cambridge Silicon Radio Ltd.");
MODULE_LICENSE("GPL");
