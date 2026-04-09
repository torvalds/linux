// SPDX-License-Identifier: GPL-2.0-only
/*
 * NVIDIA GHES vendor record handler
 *
 * Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <acpi/ghes.h>

static const guid_t nvidia_sec_guid =
	GUID_INIT(0x6d5244f2, 0x2712, 0x11ec,
		  0xbe, 0xa7, 0xcb, 0x3f, 0xdb, 0x95, 0xc7, 0x86);

struct cper_sec_nvidia {
	char	signature[16];
	__le16	error_type;
	__le16	error_instance;
	u8	severity;
	u8	socket;
	u8	number_regs;
	u8	reserved;
	__le64	instance_base;
	struct {
		__le64	addr;
		__le64	val;
	} regs[] __counted_by(number_regs);
};

struct nvidia_ghes_private {
	struct notifier_block	nb;
	struct device		*dev;
};

static void nvidia_ghes_print_error(struct device *dev,
				    const struct cper_sec_nvidia *nvidia_err,
				    size_t error_data_length, bool fatal)
{
	const char *level = fatal ? KERN_ERR : KERN_INFO;
	size_t min_size;

	dev_printk(level, dev, "signature: %.16s\n", nvidia_err->signature);
	dev_printk(level, dev, "error_type: %u\n", le16_to_cpu(nvidia_err->error_type));
	dev_printk(level, dev, "error_instance: %u\n", le16_to_cpu(nvidia_err->error_instance));
	dev_printk(level, dev, "severity: %u\n", nvidia_err->severity);
	dev_printk(level, dev, "socket: %u\n", nvidia_err->socket);
	dev_printk(level, dev, "number_regs: %u\n", nvidia_err->number_regs);
	dev_printk(level, dev, "instance_base: 0x%016llx\n",
		   le64_to_cpu(nvidia_err->instance_base));

	if (nvidia_err->number_regs == 0)
		return;

	/*
	 * Validate that all registers fit within error_data_length.
	 * Each register pair is two little-endian u64s.
	 */
	min_size = struct_size(nvidia_err, regs, nvidia_err->number_regs);
	if (error_data_length < min_size) {
		dev_err(dev, "Invalid number_regs %u (section size %zu, need %zu)\n",
			nvidia_err->number_regs, error_data_length, min_size);
		return;
	}

	for (int i = 0; i < nvidia_err->number_regs; i++)
		dev_printk(level, dev, "register[%d]: address=0x%016llx value=0x%016llx\n",
			   i, le64_to_cpu(nvidia_err->regs[i].addr),
			   le64_to_cpu(nvidia_err->regs[i].val));
}

static int nvidia_ghes_notify(struct notifier_block *nb,
			      unsigned long event, void *data)
{
	struct acpi_hest_generic_data *gdata = data;
	struct nvidia_ghes_private *priv;
	const struct cper_sec_nvidia *nvidia_err;
	guid_t sec_guid;

	import_guid(&sec_guid, gdata->section_type);
	if (!guid_equal(&sec_guid, &nvidia_sec_guid))
		return NOTIFY_DONE;

	priv = container_of(nb, struct nvidia_ghes_private, nb);

	if (acpi_hest_get_error_length(gdata) < sizeof(*nvidia_err)) {
		dev_err(priv->dev, "Section too small (%d < %zu)\n",
			acpi_hest_get_error_length(gdata), sizeof(*nvidia_err));
		return NOTIFY_OK;
	}

	nvidia_err = acpi_hest_get_payload(gdata);

	if (event >= GHES_SEV_RECOVERABLE)
		dev_err(priv->dev, "NVIDIA CPER section, error_data_length: %u\n",
			acpi_hest_get_error_length(gdata));
	else
		dev_info(priv->dev, "NVIDIA CPER section, error_data_length: %u\n",
			 acpi_hest_get_error_length(gdata));

	nvidia_ghes_print_error(priv->dev, nvidia_err, acpi_hest_get_error_length(gdata),
				event >= GHES_SEV_RECOVERABLE);

	return NOTIFY_OK;
}

static int nvidia_ghes_probe(struct platform_device *pdev)
{
	struct nvidia_ghes_private *priv;
	int ret;

	priv = devm_kmalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	*priv = (struct nvidia_ghes_private) {
		.nb.notifier_call = nvidia_ghes_notify,
		.dev = &pdev->dev,
	};

	ret = devm_ghes_register_vendor_record_notifier(&pdev->dev, &priv->nb);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to register NVIDIA GHES vendor record notifier\n");

	return 0;
}

static const struct acpi_device_id nvidia_ghes_acpi_match[] = {
	{ "NVDA2012" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, nvidia_ghes_acpi_match);

static struct platform_driver nvidia_ghes_driver = {
	.driver = {
		.name = "nvidia-ghes",
		.acpi_match_table = nvidia_ghes_acpi_match,
	},
	.probe = nvidia_ghes_probe,
};
module_platform_driver(nvidia_ghes_driver);

MODULE_AUTHOR("Kai-Heng Feng <kaihengf@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA GHES vendor CPER record handler");
MODULE_LICENSE("GPL");
