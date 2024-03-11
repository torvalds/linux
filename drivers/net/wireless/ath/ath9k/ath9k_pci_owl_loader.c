// SPDX-License-Identifier: ISC
/* Initialize Owl Emulation Devices
 *
 * Copyright (C) 2016 Christian Lamparter <chunkeey@gmail.com>
 * Copyright (C) 2016 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 *
 * Some devices (like the Cisco Meraki Z1 Cloud Managed Teleworker Gateway)
 * need to be able to initialize the PCIe wifi device. Normally, this is done
 * during the early stages as a pci quirk.
 * However, this isn't possible for devices which have the init code for the
 * Atheros chip stored on UBI Volume on NAND. Hence, this module can be used to
 * initialize the chip when the user-space is ready to extract the init code.
 */
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ath9k_platform.h>
#include <linux/nvmem-consumer.h>
#include <linux/workqueue.h>

struct owl_ctx {
	struct pci_dev *pdev;
	struct completion eeprom_load;
	struct work_struct work;
	struct nvmem_cell *cell;
};

#define EEPROM_FILENAME_LEN 100

#define AR5416_EEPROM_MAGIC 0xa55a

static int ath9k_pci_fixup(struct pci_dev *pdev, const u16 *cal_data,
			   size_t cal_len)
{
	void __iomem *mem;
	const void *cal_end = (void *)cal_data + cal_len;
	const struct {
		u16 reg;
		u16 low_val;
		u16 high_val;
	} __packed * data;
	u16 cmd;
	u32 bar0;
	bool swap_needed = false;

	/* also note that we are doing *u16 operations on the file */
	if (cal_len > 4096 || cal_len < 0x200 || (cal_len & 1) == 1) {
		dev_err(&pdev->dev, "eeprom has an invalid size.\n");
		return -EINVAL;
	}

	if (*cal_data != AR5416_EEPROM_MAGIC) {
		if (*cal_data != swab16(AR5416_EEPROM_MAGIC)) {
			dev_err(&pdev->dev, "invalid calibration data\n");
			return -EINVAL;
		}

		dev_dbg(&pdev->dev, "calibration data needs swapping\n");
		swap_needed = true;
	}

	dev_info(&pdev->dev, "fixup device configuration\n");

	mem = pcim_iomap(pdev, 0, 0);
	if (!mem) {
		dev_err(&pdev->dev, "ioremap error\n");
		return -EINVAL;
	}

	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &bar0);
	pci_write_config_dword(pdev, PCI_BASE_ADDRESS_0,
			       pci_resource_start(pdev, 0));
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	/* set pointer to first reg address */
	for (data = (const void *)(cal_data + 3);
	     (const void *)data <= cal_end && data->reg != (u16)~0;
	     data++) {
		u32 val;
		u16 reg;

		reg = data->reg;
		val = data->low_val;
		val |= ((u32)data->high_val) << 16;

		if (swap_needed) {
			reg = swab16(reg);
			val = swahb32(val);
		}

		iowrite32(val, mem + reg);
		usleep_range(100, 120);
	}

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd &= ~(PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	pci_write_config_dword(pdev, PCI_BASE_ADDRESS_0, bar0);
	pcim_iounmap(pdev, mem);

	pci_disable_device(pdev);

	return 0;
}

static void owl_rescan(struct pci_dev *pdev)
{
	struct pci_bus *bus = pdev->bus;

	pci_lock_rescan_remove();
	pci_stop_and_remove_bus_device(pdev);
	/* the device should come back with the proper
	 * ProductId. But we have to initiate a rescan.
	 */
	pci_rescan_bus(bus);
	pci_unlock_rescan_remove();
}

static void owl_fw_cb(const struct firmware *fw, void *context)
{
	struct owl_ctx *ctx = context;

	complete(&ctx->eeprom_load);

	if (fw) {
		ath9k_pci_fixup(ctx->pdev, (const u16 *)fw->data, fw->size);
		owl_rescan(ctx->pdev);
	} else {
		dev_err(&ctx->pdev->dev, "no eeprom data received.\n");
	}
	release_firmware(fw);
}

static const char *owl_get_eeprom_name(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	char *eeprom_name;

	dev_dbg(dev, "using auto-generated eeprom filename\n");

	eeprom_name = devm_kzalloc(dev, EEPROM_FILENAME_LEN, GFP_KERNEL);
	if (!eeprom_name)
		return NULL;

	/* this should match the pattern used in ath9k/init.c */
	scnprintf(eeprom_name, EEPROM_FILENAME_LEN, "ath9k-eeprom-pci-%s.bin",
		  dev_name(dev));

	return eeprom_name;
}

static void owl_nvmem_work(struct work_struct *work)
{
	struct owl_ctx *ctx = container_of(work, struct owl_ctx, work);
	void *buf;
	size_t len;

	complete(&ctx->eeprom_load);

	buf = nvmem_cell_read(ctx->cell, &len);
	if (!IS_ERR(buf)) {
		ath9k_pci_fixup(ctx->pdev, buf, len);
		kfree(buf);
		owl_rescan(ctx->pdev);
	} else {
		dev_err(&ctx->pdev->dev, "no nvmem data received.\n");
	}
}

static int owl_nvmem_probe(struct owl_ctx *ctx)
{
	int err;

	ctx->cell = devm_nvmem_cell_get(&ctx->pdev->dev, "calibration");
	if (IS_ERR(ctx->cell)) {
		err = PTR_ERR(ctx->cell);
		if (err == -ENOENT || err == -EOPNOTSUPP)
			return 1; /* not present, try firmware_request */

		return err;
	}

	INIT_WORK(&ctx->work, owl_nvmem_work);
	schedule_work(&ctx->work);

	return 0;
}

static int owl_probe(struct pci_dev *pdev,
		     const struct pci_device_id *id)
{
	struct owl_ctx *ctx;
	const char *eeprom_name;
	int err = 0;

	if (pcim_enable_device(pdev))
		return -EIO;

	pcim_pin_device(pdev);

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	init_completion(&ctx->eeprom_load);
	ctx->pdev = pdev;

	pci_set_drvdata(pdev, ctx);

	err = owl_nvmem_probe(ctx);
	if (err <= 0)
		return err;

	eeprom_name = owl_get_eeprom_name(pdev);
	if (!eeprom_name) {
		dev_err(&pdev->dev, "no eeprom filename found.\n");
		return -ENODEV;
	}

	err = request_firmware_nowait(THIS_MODULE, true, eeprom_name,
				      &pdev->dev, GFP_KERNEL, ctx, owl_fw_cb);
	if (err)
		dev_err(&pdev->dev, "failed to request caldata (%d).\n", err);

	return err;
}

static void owl_remove(struct pci_dev *pdev)
{
	struct owl_ctx *ctx = pci_get_drvdata(pdev);

	if (ctx) {
		wait_for_completion(&ctx->eeprom_load);
		pci_set_drvdata(pdev, NULL);
	}
}

static const struct pci_device_id owl_pci_table[] = {
	{ PCI_VDEVICE(ATHEROS, 0xff1c) }, /* PCIe */
	{ PCI_VDEVICE(ATHEROS, 0xff1d) }, /* PCI */
	{ },
};
MODULE_DEVICE_TABLE(pci, owl_pci_table);

static struct pci_driver owl_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= owl_pci_table,
	.probe		= owl_probe,
	.remove		= owl_remove,
};
module_pci_driver(owl_driver);
MODULE_AUTHOR("Christian Lamparter <chunkeey@gmail.com>");
MODULE_DESCRIPTION("External EEPROM data loader for Atheros AR500X to AR92XX");
MODULE_LICENSE("Dual BSD/GPL");
