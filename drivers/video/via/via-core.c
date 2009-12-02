/*
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2009 Jonathan Corbet <corbet@lwn.net>
 */

/*
 * Core code for the Via multifunction framebuffer device.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include "global.h"  	/* Includes everything under the sun */

/*
 * The default port config.
 */
static struct via_port_cfg adap_configs[] = {
	[VIA_PORT_26]	= { VIA_PORT_I2C,  VIA_MODE_OFF, VIASR, 0x26 },
	[VIA_PORT_31]	= { VIA_PORT_I2C,  VIA_MODE_I2C, VIASR, 0x31 },
	[VIA_PORT_25]	= { VIA_PORT_GPIO, VIA_MODE_GPIO, VIASR, 0x25 },
	[VIA_PORT_2C]	= { VIA_PORT_GPIO, VIA_MODE_I2C, VIASR, 0x2c },
	[VIA_PORT_3D]	= { VIA_PORT_GPIO, VIA_MODE_GPIO, VIASR, 0x3d },
	{ 0, 0, 0, 0 }
};


static int __devinit via_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	/*
	 * Create the I2C busses.  Bailing out on failure seems extreme,
	 * but that's what the code did before.
	 */
	ret = viafb_create_i2c_busses(adap_configs);
	if (ret)
		goto out_disable;
	/*
	 * Set up the framebuffer.
	 */
	ret = via_fb_pci_probe(pdev, ent);
	if (ret)
		goto out_i2c;
	return 0;

out_i2c:
	viafb_delete_i2c_busses();
out_disable:
	pci_disable_device(pdev);
	return ret;
}

static void __devexit via_pci_remove(struct pci_dev *pdev)
{
	viafb_delete_i2c_busses();
	via_fb_pci_remove(pdev);
	pci_disable_device(pdev);
}


static struct pci_device_id via_pci_table[] __devinitdata = {
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_CLE266_DID),
	  .driver_data = UNICHROME_CLE266 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_PM800_DID),
	  .driver_data = UNICHROME_PM800 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_K400_DID),
	  .driver_data = UNICHROME_K400 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_K800_DID),
	  .driver_data = UNICHROME_K800 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_P4M890_DID),
	  .driver_data = UNICHROME_CN700 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_K8M890_DID),
	  .driver_data = UNICHROME_K8M890 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_CX700_DID),
	  .driver_data = UNICHROME_CX700 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_P4M900_DID),
	  .driver_data = UNICHROME_P4M900 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_CN750_DID),
	  .driver_data = UNICHROME_CN750 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_VX800_DID),
	  .driver_data = UNICHROME_VX800 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, UNICHROME_VX855_DID),
	  .driver_data = UNICHROME_VX855 },
	{ }
};
MODULE_DEVICE_TABLE(pci, via_pci_table);

static struct pci_driver via_driver = {
	.name		= "viafb",
	.id_table	= via_pci_table,
	.probe		= via_pci_probe,
	.remove		= __devexit_p(via_pci_remove),
};

static int __init via_core_init(void)
{
	int ret;

	ret = viafb_init();
	if (ret)
		return ret;
	return pci_register_driver(&via_driver);
}

static void __exit via_core_exit(void)
{
	pci_unregister_driver(&via_driver);
	viafb_exit();
}

module_init(via_core_init);
module_exit(via_core_exit);
