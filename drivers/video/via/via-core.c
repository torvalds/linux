/*
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2009 Jonathan Corbet <corbet@lwn.net>
 */

/*
 * Core code for the Via multifunction framebuffer device.
 */
#include "via-core.h"
#include "via_i2c.h"
#include "via-gpio.h"
#include "global.h"

#include <linux/module.h>
#include <linux/platform_device.h>

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

/*
 * We currently only support one viafb device (will there ever be
 * more than one?), so just declare it globally here.
 */
static struct viafb_dev global_dev;


/*
 * Figure out how big our framebuffer memory is.  Kind of ugly,
 * but evidently we can't trust the information found in the
 * fbdev configuration area.
 */
static u16 via_function3[] = {
	CLE266_FUNCTION3, KM400_FUNCTION3, CN400_FUNCTION3, CN700_FUNCTION3,
	CX700_FUNCTION3, KM800_FUNCTION3, KM890_FUNCTION3, P4M890_FUNCTION3,
	P4M900_FUNCTION3, VX800_FUNCTION3, VX855_FUNCTION3,
};

/* Get the BIOS-configured framebuffer size from PCI configuration space
 * of function 3 in the respective chipset */
static int viafb_get_fb_size_from_pci(int chip_type)
{
	int i;
	u8 offset = 0;
	u32 FBSize;
	u32 VideoMemSize;

	/* search for the "FUNCTION3" device in this chipset */
	for (i = 0; i < ARRAY_SIZE(via_function3); i++) {
		struct pci_dev *pdev;

		pdev = pci_get_device(PCI_VENDOR_ID_VIA, via_function3[i],
				      NULL);
		if (!pdev)
			continue;

		DEBUG_MSG(KERN_INFO "Device ID = %x\n", pdev->device);

		switch (pdev->device) {
		case CLE266_FUNCTION3:
		case KM400_FUNCTION3:
			offset = 0xE0;
			break;
		case CN400_FUNCTION3:
		case CN700_FUNCTION3:
		case CX700_FUNCTION3:
		case KM800_FUNCTION3:
		case KM890_FUNCTION3:
		case P4M890_FUNCTION3:
		case P4M900_FUNCTION3:
		case VX800_FUNCTION3:
		case VX855_FUNCTION3:
		/*case CN750_FUNCTION3: */
			offset = 0xA0;
			break;
		}

		if (!offset)
			break;

		pci_read_config_dword(pdev, offset, &FBSize);
		pci_dev_put(pdev);
	}

	if (!offset) {
		printk(KERN_ERR "cannot determine framebuffer size\n");
		return -EIO;
	}

	FBSize = FBSize & 0x00007000;
	DEBUG_MSG(KERN_INFO "FB Size = %x\n", FBSize);

	if (chip_type < UNICHROME_CX700) {
		switch (FBSize) {
		case 0x00004000:
			VideoMemSize = (16 << 20);	/*16M */
			break;

		case 0x00005000:
			VideoMemSize = (32 << 20);	/*32M */
			break;

		case 0x00006000:
			VideoMemSize = (64 << 20);	/*64M */
			break;

		default:
			VideoMemSize = (32 << 20);	/*32M */
			break;
		}
	} else {
		switch (FBSize) {
		case 0x00001000:
			VideoMemSize = (8 << 20);	/*8M */
			break;

		case 0x00002000:
			VideoMemSize = (16 << 20);	/*16M */
			break;

		case 0x00003000:
			VideoMemSize = (32 << 20);	/*32M */
			break;

		case 0x00004000:
			VideoMemSize = (64 << 20);	/*64M */
			break;

		case 0x00005000:
			VideoMemSize = (128 << 20);	/*128M */
			break;

		case 0x00006000:
			VideoMemSize = (256 << 20);	/*256M */
			break;

		case 0x00007000:	/* Only on VX855/875 */
			VideoMemSize = (512 << 20);	/*512M */
			break;

		default:
			VideoMemSize = (32 << 20);	/*32M */
			break;
		}
	}

	return VideoMemSize;
}


/*
 * Figure out and map our MMIO regions.
 */
static int __devinit via_pci_setup_mmio(struct viafb_dev *vdev)
{
	/*
	 * Hook up to the device registers.
	 */
	vdev->engine_start = pci_resource_start(vdev->pdev, 1);
	vdev->engine_len = pci_resource_len(vdev->pdev, 1);
	/* If this fails, others will notice later */
	vdev->engine_mmio = ioremap_nocache(vdev->engine_start,
			vdev->engine_len);

	/*
	 * Likewise with I/O memory.
	 */
	vdev->fbmem_start = pci_resource_start(vdev->pdev, 0);
	vdev->fbmem_len = viafb_get_fb_size_from_pci(vdev->chip_type);
	if (vdev->fbmem_len < 0)
		return vdev->fbmem_len;
	vdev->fbmem = ioremap_nocache(vdev->fbmem_start, vdev->fbmem_len);
	if (vdev->fbmem == NULL)
		return -ENOMEM;
	return 0;
}

static void __devexit via_pci_teardown_mmio(struct viafb_dev *vdev)
{
	iounmap(vdev->fbmem);
	iounmap(vdev->engine_mmio);
}


static int __devinit via_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	/*
	 * Global device initialization.
	 */
	memset(&global_dev, 0, sizeof(global_dev));
	global_dev.pdev = pdev;
	global_dev.chip_type = ent->driver_data;
	spin_lock_init(&global_dev.reg_lock);
	ret = via_pci_setup_mmio(&global_dev);
	if (ret)
		goto out_disable;
	/*
	 * Create the I2C busses.  Bailing out on failure seems extreme,
	 * but that's what the code did before.
	 */
	ret = viafb_create_i2c_busses(adap_configs);
	if (ret)
		goto out_teardown;
	/*
	 * Set up the framebuffer.
	 */
	ret = via_fb_pci_probe(&global_dev);
	if (ret)
		goto out_i2c;
	/*
	 * Create the GPIOs.  We continue whether or not this succeeds;
	 * the framebuffer might be useful even without GPIO ports.
	 */
	ret = viafb_create_gpios(&global_dev, adap_configs);
	return 0;

out_i2c:
	viafb_delete_i2c_busses();
out_teardown:
	via_pci_teardown_mmio(&global_dev);
out_disable:
	pci_disable_device(pdev);
	return ret;
}

static void __devexit via_pci_remove(struct pci_dev *pdev)
{
	viafb_destroy_gpios();
	viafb_delete_i2c_busses();
	via_fb_pci_remove(pdev);
	via_pci_teardown_mmio(&global_dev);
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
