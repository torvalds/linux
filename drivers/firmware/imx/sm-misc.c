// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/debugfs.h>
#include <linux/device/devres.h>
#include <linux/firmware/imx/sm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_imx_protocol.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>

static const struct scmi_imx_misc_proto_ops *imx_misc_ctrl_ops;
static struct scmi_protocol_handle *ph;
struct notifier_block scmi_imx_misc_ctrl_nb;

int scmi_imx_misc_ctrl_set(u32 id, u32 val)
{
	if (!ph)
		return -EPROBE_DEFER;

	return imx_misc_ctrl_ops->misc_ctrl_set(ph, id, 1, &val);
};
EXPORT_SYMBOL(scmi_imx_misc_ctrl_set);

int scmi_imx_misc_ctrl_get(u32 id, u32 *num, u32 *val)
{
	if (!ph)
		return -EPROBE_DEFER;

	return imx_misc_ctrl_ops->misc_ctrl_get(ph, id, num, val);
}
EXPORT_SYMBOL(scmi_imx_misc_ctrl_get);

static int scmi_imx_misc_ctrl_notifier(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	/*
	 * notifier_chain_register requires a valid notifier_block and
	 * valid notifier_call. SCMI_EVENT_IMX_MISC_CONTROL is needed
	 * to let SCMI firmware enable control events, but the hook here
	 * is just a dummy function to avoid kernel panic as of now.
	 */
	return 0;
}

static int syslog_show(struct seq_file *file, void *priv)
{
	/* 4KB is large enough for syslog */
	void *syslog __free(kfree) = kmalloc(SZ_4K, GFP_KERNEL);
	/* syslog API use num words, not num bytes */
	u16 size = SZ_4K / 4;
	int ret;

	if (!ph)
		return -ENODEV;

	ret = imx_misc_ctrl_ops->misc_syslog(ph, &size, syslog);
	if (ret)
		return ret;

	seq_hex_dump(file, " ", DUMP_PREFIX_NONE, 16, sizeof(u32), syslog, size * 4, false);
	seq_putc(file, '\n');

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(syslog);

static void scmi_imx_misc_put(void *p)
{
	debugfs_remove((struct dentry *)p);
}

static int scmi_imx_misc_ctrl_probe(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	struct device_node *np = sdev->dev.of_node;
	struct dentry *scmi_imx_dentry;
	u32 src_id, flags;
	int ret, i, num;

	if (!handle)
		return -ENODEV;

	if (imx_misc_ctrl_ops) {
		dev_err(&sdev->dev, "misc ctrl already initialized\n");
		return -EEXIST;
	}

	imx_misc_ctrl_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_IMX_MISC, &ph);
	if (IS_ERR(imx_misc_ctrl_ops))
		return PTR_ERR(imx_misc_ctrl_ops);

	num = of_property_count_u32_elems(np, "nxp,ctrl-ids");
	if (num % 2) {
		dev_err(&sdev->dev, "Invalid wakeup-sources\n");
		return -EINVAL;
	}

	scmi_imx_misc_ctrl_nb.notifier_call = &scmi_imx_misc_ctrl_notifier;
	for (i = 0; i < num; i += 2) {
		ret = of_property_read_u32_index(np, "nxp,ctrl-ids", i, &src_id);
		if (ret) {
			dev_err(&sdev->dev, "Failed to read ctrl-id: %i\n", i);
			continue;
		}

		ret = of_property_read_u32_index(np, "nxp,ctrl-ids", i + 1, &flags);
		if (ret) {
			dev_err(&sdev->dev, "Failed to read ctrl-id value: %d\n", i + 1);
			continue;
		}

		ret = handle->notify_ops->devm_event_notifier_register(sdev, SCMI_PROTOCOL_IMX_MISC,
								       SCMI_EVENT_IMX_MISC_CONTROL,
								       &src_id,
								       &scmi_imx_misc_ctrl_nb);
		if (ret) {
			dev_err(&sdev->dev, "Failed to register scmi misc event: %d\n", src_id);
		} else {
			ret = imx_misc_ctrl_ops->misc_ctrl_req_notify(ph, src_id,
								      SCMI_EVENT_IMX_MISC_CONTROL,
								      flags);
			if (ret)
				dev_err(&sdev->dev, "Failed to req notify: %d\n", src_id);
		}
	}

	scmi_imx_dentry = debugfs_create_dir("scmi_imx", NULL);
	debugfs_create_file("syslog", 0444, scmi_imx_dentry, &sdev->dev, &syslog_fops);

	return devm_add_action_or_reset(&sdev->dev, scmi_imx_misc_put, scmi_imx_dentry);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_IMX_MISC, "imx-misc-ctrl" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_imx_misc_ctrl_driver = {
	.name = "scmi-imx-misc-ctrl",
	.probe = scmi_imx_misc_ctrl_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_imx_misc_ctrl_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("IMX SM MISC driver");
MODULE_LICENSE("GPL");
