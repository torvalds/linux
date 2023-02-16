// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for handling the PCIe controller errors on
 * HiSilicon HIP SoCs.
 *
 * Copyright (c) 2020 HiSilicon Limited.
 */

#include <linux/acpi.h>
#include <acpi/ghes.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>

/* HISI PCIe controller error definitions */
#define HISI_PCIE_ERR_MISC_REGS	33

#define HISI_PCIE_LOCAL_VALID_VERSION		BIT(0)
#define HISI_PCIE_LOCAL_VALID_SOC_ID		BIT(1)
#define HISI_PCIE_LOCAL_VALID_SOCKET_ID		BIT(2)
#define HISI_PCIE_LOCAL_VALID_NIMBUS_ID		BIT(3)
#define HISI_PCIE_LOCAL_VALID_SUB_MODULE_ID	BIT(4)
#define HISI_PCIE_LOCAL_VALID_CORE_ID		BIT(5)
#define HISI_PCIE_LOCAL_VALID_PORT_ID		BIT(6)
#define HISI_PCIE_LOCAL_VALID_ERR_TYPE		BIT(7)
#define HISI_PCIE_LOCAL_VALID_ERR_SEVERITY	BIT(8)
#define HISI_PCIE_LOCAL_VALID_ERR_MISC		9

static guid_t hisi_pcie_sec_guid =
	GUID_INIT(0xB2889FC9, 0xE7D7, 0x4F9D,
		  0xA8, 0x67, 0xAF, 0x42, 0xE9, 0x8B, 0xE7, 0x72);

/*
 * Firmware reports the socket port ID where the error occurred.  These
 * macros convert that to the core ID and core port ID required by the
 * ACPI reset method.
 */
#define HISI_PCIE_PORT_ID(core, v)       (((v) >> 1) + ((core) << 3))
#define HISI_PCIE_CORE_ID(v)             ((v) >> 3)
#define HISI_PCIE_CORE_PORT_ID(v)        (((v) & 7) << 1)

struct hisi_pcie_error_data {
	u64	val_bits;
	u8	version;
	u8	soc_id;
	u8	socket_id;
	u8	nimbus_id;
	u8	sub_module_id;
	u8	core_id;
	u8	port_id;
	u8	err_severity;
	u16	err_type;
	u8	reserv[2];
	u32	err_misc[HISI_PCIE_ERR_MISC_REGS];
};

struct hisi_pcie_error_private {
	struct notifier_block	nb;
	struct device *dev;
};

enum hisi_pcie_submodule_id {
	HISI_PCIE_SUB_MODULE_ID_AP,
	HISI_PCIE_SUB_MODULE_ID_TL,
	HISI_PCIE_SUB_MODULE_ID_MAC,
	HISI_PCIE_SUB_MODULE_ID_DL,
	HISI_PCIE_SUB_MODULE_ID_SDI,
};

static const char * const hisi_pcie_sub_module[] = {
	[HISI_PCIE_SUB_MODULE_ID_AP]	= "AP Layer",
	[HISI_PCIE_SUB_MODULE_ID_TL]	= "TL Layer",
	[HISI_PCIE_SUB_MODULE_ID_MAC]	= "MAC Layer",
	[HISI_PCIE_SUB_MODULE_ID_DL]	= "DL Layer",
	[HISI_PCIE_SUB_MODULE_ID_SDI]	= "SDI Layer",
};

enum hisi_pcie_err_severity {
	HISI_PCIE_ERR_SEV_RECOVERABLE,
	HISI_PCIE_ERR_SEV_FATAL,
	HISI_PCIE_ERR_SEV_CORRECTED,
	HISI_PCIE_ERR_SEV_NONE,
};

static const char * const hisi_pcie_error_sev[] = {
	[HISI_PCIE_ERR_SEV_RECOVERABLE]	= "recoverable",
	[HISI_PCIE_ERR_SEV_FATAL]	= "fatal",
	[HISI_PCIE_ERR_SEV_CORRECTED]	= "corrected",
	[HISI_PCIE_ERR_SEV_NONE]	= "none",
};

static const char *hisi_pcie_get_string(const char * const *array,
					size_t n, u32 id)
{
	u32 index;

	for (index = 0; index < n; index++) {
		if (index == id && array[index])
			return array[index];
	}

	return "unknown";
}

static int hisi_pcie_port_reset(struct platform_device *pdev,
				u32 chip_id, u32 port_id)
{
	struct device *dev = &pdev->dev;
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object arg[3];
	struct acpi_object_list arg_list;
	acpi_status s;
	unsigned long long data = 0;

	arg[0].type = ACPI_TYPE_INTEGER;
	arg[0].integer.value = chip_id;
	arg[1].type = ACPI_TYPE_INTEGER;
	arg[1].integer.value = HISI_PCIE_CORE_ID(port_id);
	arg[2].type = ACPI_TYPE_INTEGER;
	arg[2].integer.value = HISI_PCIE_CORE_PORT_ID(port_id);

	arg_list.count = 3;
	arg_list.pointer = arg;

	s = acpi_evaluate_integer(handle, "RST", &arg_list, &data);
	if (ACPI_FAILURE(s)) {
		dev_err(dev, "No RST method\n");
		return -EIO;
	}

	if (data) {
		dev_err(dev, "Failed to Reset\n");
		return -EIO;
	}

	return 0;
}

static int hisi_pcie_port_do_recovery(struct platform_device *dev,
				      u32 chip_id, u32 port_id)
{
	acpi_status s;
	struct device *device = &dev->dev;
	acpi_handle root_handle = ACPI_HANDLE(device);
	struct acpi_pci_root *pci_root;
	struct pci_bus *root_bus;
	struct pci_dev *pdev;
	u32 domain, busnr, devfn;

	s = acpi_get_parent(root_handle, &root_handle);
	if (ACPI_FAILURE(s))
		return -ENODEV;
	pci_root = acpi_pci_find_root(root_handle);
	if (!pci_root)
		return -ENODEV;
	root_bus = pci_root->bus;
	domain = pci_root->segment;

	busnr = root_bus->number;
	devfn = PCI_DEVFN(port_id, 0);
	pdev = pci_get_domain_bus_and_slot(domain, busnr, devfn);
	if (!pdev) {
		dev_info(device, "Fail to get root port %04x:%02x:%02x.%d device\n",
			 domain, busnr, PCI_SLOT(devfn), PCI_FUNC(devfn));
		return -ENODEV;
	}

	pci_stop_and_remove_bus_device_locked(pdev);
	pci_dev_put(pdev);

	if (hisi_pcie_port_reset(dev, chip_id, port_id))
		return -EIO;

	/*
	 * The initialization time of subordinate devices after
	 * hot reset is no more than 1s, which is required by
	 * the PCI spec v5.0 sec 6.6.1. The time will shorten
	 * if Readiness Notifications mechanisms are used. But
	 * wait 1s here to adapt any conditions.
	 */
	ssleep(1UL);

	/* add root port and downstream devices */
	pci_lock_rescan_remove();
	pci_rescan_bus(root_bus);
	pci_unlock_rescan_remove();

	return 0;
}

static void hisi_pcie_handle_error(struct platform_device *pdev,
				   const struct hisi_pcie_error_data *edata)
{
	struct device *dev = &pdev->dev;
	int idx, rc;
	const unsigned long valid_bits[] = {BITMAP_FROM_U64(edata->val_bits)};

	if (edata->val_bits == 0) {
		dev_warn(dev, "%s: no valid error information\n", __func__);
		return;
	}

	dev_info(dev, "\nHISI : HIP : PCIe controller error\n");
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_SOC_ID)
		dev_info(dev, "Table version = %d\n", edata->version);
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_SOCKET_ID)
		dev_info(dev, "Socket ID = %d\n", edata->socket_id);
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_NIMBUS_ID)
		dev_info(dev, "Nimbus ID = %d\n", edata->nimbus_id);
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_SUB_MODULE_ID)
		dev_info(dev, "Sub Module = %s\n",
			 hisi_pcie_get_string(hisi_pcie_sub_module,
					      ARRAY_SIZE(hisi_pcie_sub_module),
					      edata->sub_module_id));
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_CORE_ID)
		dev_info(dev, "Core ID = core%d\n", edata->core_id);
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_PORT_ID)
		dev_info(dev, "Port ID = port%d\n", edata->port_id);
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_ERR_SEVERITY)
		dev_info(dev, "Error severity = %s\n",
			 hisi_pcie_get_string(hisi_pcie_error_sev,
					      ARRAY_SIZE(hisi_pcie_error_sev),
					      edata->err_severity));
	if (edata->val_bits & HISI_PCIE_LOCAL_VALID_ERR_TYPE)
		dev_info(dev, "Error type = 0x%x\n", edata->err_type);

	dev_info(dev, "Reg Dump:\n");
	idx = HISI_PCIE_LOCAL_VALID_ERR_MISC;
	for_each_set_bit_from(idx, valid_bits,
			      HISI_PCIE_LOCAL_VALID_ERR_MISC + HISI_PCIE_ERR_MISC_REGS)
		dev_info(dev, "ERR_MISC_%d = 0x%x\n", idx - HISI_PCIE_LOCAL_VALID_ERR_MISC,
			 edata->err_misc[idx - HISI_PCIE_LOCAL_VALID_ERR_MISC]);

	if (edata->err_severity != HISI_PCIE_ERR_SEV_RECOVERABLE)
		return;

	/* Recovery for the PCIe controller errors, try reset
	 * PCI port for the error recovery
	 */
	rc = hisi_pcie_port_do_recovery(pdev, edata->socket_id,
			HISI_PCIE_PORT_ID(edata->core_id, edata->port_id));
	if (rc)
		dev_info(dev, "fail to do hisi pcie port reset\n");
}

static int hisi_pcie_notify_error(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct acpi_hest_generic_data *gdata = data;
	const struct hisi_pcie_error_data *error_data = acpi_hest_get_payload(gdata);
	struct hisi_pcie_error_private *priv;
	struct device *dev;
	struct platform_device *pdev;
	guid_t err_sec_guid;
	u8 socket;

	import_guid(&err_sec_guid, gdata->section_type);
	if (!guid_equal(&err_sec_guid, &hisi_pcie_sec_guid))
		return NOTIFY_DONE;

	priv = container_of(nb, struct hisi_pcie_error_private, nb);
	dev = priv->dev;

	if (device_property_read_u8(dev, "socket", &socket))
		return NOTIFY_DONE;

	if (error_data->socket_id != socket)
		return NOTIFY_DONE;

	pdev = container_of(dev, struct platform_device, dev);
	hisi_pcie_handle_error(pdev, error_data);

	return NOTIFY_OK;
}

static int hisi_pcie_error_handler_probe(struct platform_device *pdev)
{
	struct hisi_pcie_error_private *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->nb.notifier_call = hisi_pcie_notify_error;
	priv->dev = &pdev->dev;
	ret = ghes_register_vendor_record_notifier(&priv->nb);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to register hisi pcie controller error handler with apei\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int hisi_pcie_error_handler_remove(struct platform_device *pdev)
{
	struct hisi_pcie_error_private *priv = platform_get_drvdata(pdev);

	ghes_unregister_vendor_record_notifier(&priv->nb);

	return 0;
}

static const struct acpi_device_id hisi_pcie_acpi_match[] = {
	{ "HISI0361", 0 },
	{ }
};

static struct platform_driver hisi_pcie_error_handler_driver = {
	.driver = {
		.name	= "hisi-pcie-error-handler",
		.acpi_match_table = hisi_pcie_acpi_match,
	},
	.probe		= hisi_pcie_error_handler_probe,
	.remove		= hisi_pcie_error_handler_remove,
};
module_platform_driver(hisi_pcie_error_handler_driver);

MODULE_DESCRIPTION("HiSilicon HIP PCIe controller error handling driver");
